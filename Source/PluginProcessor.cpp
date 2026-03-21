#include "PluginProcessor.h"
#include "PluginEditor.h"

// Define JUCE plugin macros that are missing in cmake setup
#define JucePlugin_Name "Instant Mix"

juce::AudioProcessorValueTreeState::ParameterLayout LeviathexInstantMixerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("input_gain", "Input Gain", 0.0f, 100.0f, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output_gain", "Output Gain", 0.0f, 100.0f, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> ("instrument", "Instrument", 0, 2, 0));
    
    // Stereo width control (0-200%, default 100%)
    layout.add (std::make_unique<juce::AudioParameterFloat> ("stereo_width", "Stereo Width", 0.0f, 200.0f, 100.0f));
    
    // Reverb parameters
    layout.add (std::make_unique<juce::AudioParameterBool> ("reverb_enabled", "Reverb Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverb_length", "Reverb Length", 0.5f, 4.0f, 2.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("reverb_send_to_bus", "Reverb Send to Bus", false));
    
    // Loudness compensation parameters
    layout.add (std::make_unique<juce::AudioParameterBool> ("auto_makeup", "Auto Makeup", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output_trim", "Output Trim", -24.0f, 12.0f, 0.0f));
    
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
    stereoWidthParam = parameters.getRawParameterValue ("stereo_width");
    reverbEnabledParam = parameters.getRawParameterValue ("reverb_enabled");
    reverbLengthParam = parameters.getRawParameterValue ("reverb_length");
    reverbSendToBusParam = parameters.getRawParameterValue ("reverb_send_to_bus");
    autoMakeupParam = parameters.getRawParameterValue ("auto_makeup");
    outputTrimParam = parameters.getRawParameterValue ("output_trim");
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
    // Return reverb tail length when enabled
    if (reverbEnabledParam && reverbEnabledParam->load() > 0.5f)
    {
        float length = reverbLengthParam ? reverbLengthParam->load() : 2.0f;
        return static_cast<double> (length * 1.5f); // Extra time for reverb decay
    }
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
    
    // Pre-allocate ramp buffers to avoid heap allocation in processBlock
    inputGainRamp.resize (static_cast<size_t> (samplesPerBlock));
    outputGainRamp.resize (static_cast<size_t> (samplesPerBlock));
    mixRamp.resize (static_cast<size_t> (samplesPerBlock));
    
    // Initialize compressor coefficients
    cachedSampleRate = currentSampleRate;
    attackCoeff = std::exp (-1.0f / (currentSampleRate * 0.0003f));
    releaseCoeff = std::exp (-1.0f / (currentSampleRate * 0.1f));
    
    // Reset all states
    for (auto& chStates : eqStates)
        for (auto& state : chStates)
            state.reset();
    for (auto& state : compressorStates)
        state.reset();
    for (auto& state : exciterStates)
        state.reset();
    linkedEnvelope = 0.0f;
    
    // Initialize EQ coefficients (identity passthrough)
    for (auto& coeffs : eqCoeffs)
    {
        coeffs.a0 = 1.0f;
        coeffs.a1 = coeffs.a2 = coeffs.b1 = coeffs.b2 = 0.0f;
    }
    
    // Initialize 2x oversampling for exciter (always-on for cleaner saturation)
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        2,  // Number of channels
        1,  // Oversampling factor (1 = 2x, 2 = 4x, etc.)
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true  // Use steep filter
    );
    oversampling->initProcessing (static_cast<size_t> (samplesPerBlock));
    
    // Initialize reverb with default parameters
    reverbParams.roomSize = 0.5f;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = 0.0f;
    reverbParams.dryLevel = 1.0f;
    reverbParams.width = 1.0f;
    reverbProcessor.setParameters (reverbParams);
    reverbProcessor.setSampleRate (currentSampleRate);
    
    // Pre-allocate reverb buffer
    reverbBuffer.setSize (2, samplesPerBlock);
    reverbBuffer.clear();
    
    // Initialize loudness compensation
    int lufsSamples = static_cast<int> (currentSampleRate * lufsWindowSize);
    lufsInputHistory.resize (static_cast<size_t> (lufsSamples), 0.0f);
    lufsOutputHistory.resize (static_cast<size_t> (lufsSamples), 0.0f);
    lufsHistoryIndex = 0;
    measuredInputLUFS = -70.0f;
    measuredOutputLUFS = -70.0f;
    currentMakeupGain = 1.0f;
    makeupGainSmoother.reset (currentSampleRate, 0.1); // 100ms smoothing
    makeupGainSmoother.setCurrentAndTargetValue (1.0f);
    
    // Build initial EQ
    rebuildEQ (static_cast<int> (*instrumentParam), initMixAmt);
}

void LeviathexInstantMixerAudioProcessor::releaseResources()
{
    // Free pre-allocated buffers
    inputGainRamp.clear();
    inputGainRamp.shrink_to_fit();
    outputGainRamp.clear();
    outputGainRamp.shrink_to_fit();
    mixRamp.clear();
    mixRamp.shrink_to_fit();
    lufsInputHistory.clear();
    lufsInputHistory.shrink_to_fit();
    lufsOutputHistory.clear();
    lufsOutputHistory.shrink_to_fit();
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
    
    // Check if reverb is enabled and configure it
    bool reverbEnabled = reverbEnabledParam && reverbEnabledParam->load() > 0.5f;
    bool sendToBus = reverbSendToBusParam && reverbSendToBusParam->load() > 0.5f;
    float reverbLength = reverbLengthParam ? reverbLengthParam->load() : 2.0f;
    
    if (reverbEnabled)
    {
        configureReverbForInstrument (instrument, reverbLength);
    }
    
    // Pre-compute per-sample gain ramps ONCE — both channels use the same values
    // Using pre-allocated vectors to avoid heap allocation
    int processChannels = std::min (totalNumInputChannels, 2);
    
    // Ensure buffers are large enough (resize if block size changed)
    if ((int) inputGainRamp.size() < numSamples)
    {
        inputGainRamp.resize (static_cast<size_t> (numSamples));
        outputGainRamp.resize (static_cast<size_t> (numSamples));
        mixRamp.resize (static_cast<size_t> (numSamples));
        reverbBuffer.setSize (2, numSamples, false, true);
    }
    
    for (int i = 0; i < numSamples; ++i)
    {
        inputGainSmoother.getNextValue();
        outputGainSmoother.getNextValue();
        mixSmoother.getNextValue();
        
        inputGainRamp[i]  = inputGainSmoother.getCurrentValue();
        outputGainRamp[i] = outputGainSmoother.getCurrentValue();
        mixRamp[i]        = mixSmoother.getCurrentValue();
    }
    
    const float* inGain  = inputGainRamp.data();
    const float* outGain = outputGainRamp.data();
    const float* mixVal  = mixRamp.data();
    
    // Process stereo channels together for proper linking
    if (processChannels == 2)
    {
        auto* leftData = buffer.getWritePointer (0);
        auto* rightData = buffer.getWritePointer (1);
        
        // Calculate input levels for metering
        inputLevel[0] = calculateRMS (leftData, numSamples);
        inputLevel[1] = calculateRMS (rightData, numSamples);
        
        float leftPeak = 0.0f, rightPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            leftPeak = std::max (leftPeak, std::abs (leftData[sample]));
            rightPeak = std::max (rightPeak, std::abs (rightData[sample]));
        }
        inputPeak[0] = leftPeak;
        inputPeak[1] = rightPeak;
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = leftData[sample];
            float right = rightData[sample];
            
            float sm = mixVal[sample]; // Mix: 0.0 to 1.0
            
            // At 0% mix, pass through completely dry - no processing at all
            if (sm < 0.001f)
            {
                // Pure bypass - no gain changes, no processing
                leftData[sample] = left;
                rightData[sample] = right;
                continue;
            }
            
            // ADAPTIVE GAIN STAGING: Prevent clipping at high mix values
            // Reduce input gain progressively from 50% to 100% mix
            float adaptiveAttenuation = 1.0f;
            if (sm > 0.5f)
            {
                // 50-70%: -3dB gradual reduction
                // 70-100%: -6dB to prevent clipping
                float mixAbove50 = (sm - 0.5f) / 0.5f; // 0.0 to 1.0
                adaptiveAttenuation = juce::Decibels::decibelsToGain (-6.0f * mixAbove50);
            }
            
            // 1. Input gain with adaptive attenuation (only when mix > 0)
            left  *= inGain[sample] * adaptiveAttenuation;
            right *= inGain[sample] * adaptiveAttenuation;
            
            // Calculate blend amounts for each stage
            // Stage 1 (Exciter): 0-30% of mix
            float blendA = juce::jmin (sm * 3.333f, 1.0f) * juce::jmin (sm * 3.333f, 1.0f); // 0-30% -> 0-1
            // Stage 2 (EQ+Comp): 0-60% of mix  
            float blendB = (sm > 0.2f) ? juce::jmin ((sm - 0.2f) * 2.5f, 1.0f) : 0.0f; // 20-60% -> 0-1
            // Stage 3 (Polish): 40-100% of mix
            float blendC = (sm > 0.4f) ? juce::jmin ((sm - 0.4f) * 1.667f, 1.0f) : 0.0f; // 40-100% -> 0-1
            
            if (sm > 0.0f)
            {
                // Stage 1: Exciter (tape-style saturation)
                // Reduce exciter intensity at high mix to prevent distortion
                if (blendA > 0.0f)
                {
                    float exciterIntensity = blendA * (1.0f - sm * 0.3f); // Reduce by up to 30% at full mix
                    applyExciter (left, exciterStates[0], exciterIntensity);
                    applyExciter (right, exciterStates[1], exciterIntensity);
                }
                
                // Stage 2: EQ + Compression
                // Apply EQ then compressor
                if (blendB > 0.0f)
                {
                    // Store dry signal for blending
                    float dryL = left;
                    float dryR = right;
                    
                    // EQ (6 bands)
                    for (int stage = 0; stage < 6; ++stage)
                    {
                        processBiquad (left,  eqStates[0][stage], eqCoeffs[stage]);
                        processBiquad (right, eqStates[1][stage], eqCoeffs[stage]);
                    }
                    
                    // Compressor (stereo-linked)
                    applyCompressor (left, right, blendB, instrument);
                    
                    // Blend based on stage intensity
                    left = dryL + (left - dryL) * blendB;
                    right = dryR + (right - dryR) * blendB;
                }
                
                // Stage 3: Polish (limiter for safety)
                // Always apply limiter but blend intensity
                if (blendC > 0.0f)
                {
                    float preLimitL = left;
                    float preLimitR = right;
                    
                    applyLimiter (left);
                    applyLimiter (right);
                    
                    // Only apply limiting if it's actually reducing level
                    if (std::abs (left) < std::abs (preLimitL))
                        left = preLimitL + (left - preLimitL) * blendC;
                    if (std::abs (right) < std::abs (preLimitR))
                        right = preLimitR + (right - preLimitR) * blendC;
                }
            }
            
            // 5. Output gain
            left  *= outGain[sample];
            right *= outGain[sample];
            
            // 6. NaN/Inf brickwall
            if (! std::isfinite (left))  left  = 0.0f;
            if (! std::isfinite (right)) right = 0.0f;
            
            leftData[sample]  = left;
            rightData[sample] = right;
        }
        
        // Calculate output levels for metering
        outputLevel[0] = calculateRMS (leftData, numSamples);
        outputLevel[1] = calculateRMS (rightData, numSamples);
        
        float outLeftPeak = 0.0f, outRightPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            outLeftPeak  = std::max (outLeftPeak,  std::abs (leftData[sample]));
            outRightPeak = std::max (outRightPeak, std::abs (rightData[sample]));
        }
        outputPeak[0] = outLeftPeak;
        outputPeak[1] = outRightPeak;
        
        // Apply reverb if enabled
        if (reverbEnabled && !sendToBus)
        {
            // Copy processed output to reverb buffer
            reverbBuffer.copyFrom (0, 0, leftData, numSamples);
            reverbBuffer.copyFrom (1, 0, rightData, numSamples);
            
            // Process reverb
            reverbProcessor.processStereo (reverbBuffer.getWritePointer (0), 
                                           reverbBuffer.getWritePointer (1), numSamples);
            
            // Blend reverb back into main output (wet/dry mix)
            float reverbWetLevel = 0.3f; // 30% wet is a good starting point
            for (int sample = 0; sample < numSamples; ++sample)
            {
                leftData[sample]  += reverbBuffer.getSample (0, sample) * reverbWetLevel;
                rightData[sample] += reverbBuffer.getSample (1, sample) * reverbWetLevel;
            }
        }
        else if (reverbEnabled && sendToBus)
        {
            // For bus output, we need additional output channels
            // This would require multi-bus configuration in CMakeLists.txt
            // For now, just process reverb but don't add to main output
            reverbBuffer.copyFrom (0, 0, leftData, numSamples);
            reverbBuffer.copyFrom (1, 0, rightData, numSamples);
            reverbProcessor.processStereo (reverbBuffer.getWritePointer (0), 
                                           reverbBuffer.getWritePointer (1), numSamples);
        }
        
        // Loudness compensation - update makeup gain target
        bool autoMakeupEnabled = autoMakeupParam && autoMakeupParam->load() > 0.5f;
        updateMakeupGain (mixValue, autoMakeupEnabled);
        
        // Get output trim value (-24dB to +12dB)
        float outputTrimDb = outputTrimParam ? outputTrimParam->load() : 0.0f;
        float outputTrimGain = juce::Decibels::decibelsToGain (outputTrimDb);
        
        // Apply makeup gain and output trim to final output
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float makeup = makeupGainSmoother.getNextValue();
            float totalGain = makeup * outputTrimGain;
            
            leftData[sample]  *= totalGain;
            rightData[sample] *= totalGain;
            
            // NaN/Inf safety
            if (! std::isfinite (leftData[sample]))  leftData[sample] = 0.0f;
            if (! std::isfinite (rightData[sample])) rightData[sample] = 0.0f;
        }
        
        // Apply stereo width control (0-200%, default 100%)
        float stereoWidth = stereoWidthParam ? stereoWidthParam->load() / 100.0f : 1.0f;
        if (stereoWidth != 1.0f)
        {
            for (int sample = 0; sample < numSamples; ++sample)
            {
                // Mid/Side processing
                float mid = (leftData[sample] + rightData[sample]) * 0.5f;
                float side = (leftData[sample] - rightData[sample]) * 0.5f;
                
                // Apply width to side signal
                side *= stereoWidth;
                
                // Reconstruct L/R
                leftData[sample] = mid + side;
                rightData[sample] = mid - side;
            }
        }
    }
    else if (processChannels == 1)
    {
        // Mono processing
        auto* channelData = buffer.getWritePointer (0);
        
        // Calculate input levels for metering
        inputLevel[0] = calculateRMS (channelData, numSamples);
        
        float channelPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
            channelPeak = std::max (channelPeak, std::abs (channelData[sample]));
        inputPeak[0] = channelPeak;
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float x = channelData[sample];
            
            // 1. Input gain
            x *= inGain[sample];
            
            float sm = mixVal[sample];
            
            // Calculate blend amounts for each stage
            float blendA = juce::jmin (sm * 3.333f, 1.0f) * juce::jmin (sm * 3.333f, 1.0f);
            float blendB = (sm > 0.2f) ? juce::jmin ((sm - 0.2f) * 2.5f, 1.0f) : 0.0f;
            float blendC = (sm > 0.4f) ? juce::jmin ((sm - 0.4f) * 1.667f, 1.0f) : 0.0f;
            
            if (sm > 0.0f)
            {
                // Stage 1: Exciter
                if (blendA > 0.0f)
                    applyExciter (x, exciterStates[0], blendA);
                
                // Stage 2: EQ + Compression
                if (blendB > 0.0f)
                {
                    float dry = x;
                    
                    for (int stage = 0; stage < 6; ++stage)
                        processBiquad (x, eqStates[0][stage], eqCoeffs[stage]);
                    
                    float dummy = 0.0f;
                    applyCompressor (x, dummy, blendB, instrument);
                    
                    x = dry + (x - dry) * blendB;
                }
                
                // Stage 3: Polish (limiter)
                if (blendC > 0.0f)
                {
                    float preLimit = x;
                    applyLimiter (x);
                    if (std::abs (x) < std::abs (preLimit))
                        x = preLimit + (x - preLimit) * blendC;
                }
            }
            
            // 5. Output gain
            x *= outGain[sample];
            
            // 6. NaN/Inf brickwall
            if (! std::isfinite (x)) x = 0.0f;
            
            channelData[sample] = x;
        }
        
        // Calculate output levels for metering
        outputLevel[0] = calculateRMS (channelData, numSamples);
        
        float outputChannelPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
            outputChannelPeak = std::max (outputChannelPeak, std::abs (channelData[sample]));
        outputPeak[0] = outputChannelPeak;
        
        // Apply reverb if enabled (mono to stereo reverb)
        if (reverbEnabled && !sendToBus)
        {
            // Copy mono to both reverb channels
            reverbBuffer.copyFrom (0, 0, channelData, numSamples);
            reverbBuffer.copyFrom (1, 0, channelData, numSamples);
            
            // Process reverb in stereo
            reverbProcessor.processStereo (reverbBuffer.getWritePointer (0), 
                                           reverbBuffer.getWritePointer (1), numSamples);
            
            // Blend reverb back into mono output (sum both channels)
            float reverbWetLevel = 0.3f;
            for (int sample = 0; sample < numSamples; ++sample)
            {
                float wetL = reverbBuffer.getSample (0, sample);
                float wetR = reverbBuffer.getSample (1, sample);
                channelData[sample] += (wetL + wetR) * 0.5f * reverbWetLevel;
            }
        }
        
        // Loudness compensation for mono
        bool autoMakeupEnabled = autoMakeupParam && autoMakeupParam->load() > 0.5f;
        updateMakeupGain (mixValue, autoMakeupEnabled);
        
        float outputTrimDb = outputTrimParam ? outputTrimParam->load() : 0.0f;
        float outputTrimGain = juce::Decibels::decibelsToGain (outputTrimDb);
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float makeup = makeupGainSmoother.getNextValue();
            float totalGain = makeup * outputTrimGain;
            
            channelData[sample] *= totalGain;
            
            if (! std::isfinite (channelData[sample])) channelData[sample] = 0.0f;
        }
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

// EQ coefficient calculation helpers
static BiquadCoeffs calcHighPass (float freq, float Q, float sampleRate)
{
    float w0 = juce::MathConstants<float>::twoPi * freq / sampleRate;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    
    BiquadCoeffs c;
    float b0 = (1.0f + cosw0) / 2.0f;
    float b1 = -(1.0f + cosw0);
    float b2 = (1.0f + cosw0) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;
    
    c.a0 = b0 / a0;
    c.a1 = b1 / a0;
    c.a2 = b2 / a0;
    c.b1 = a1 / a0;
    c.b2 = a2 / a0;
    return c;
}

static BiquadCoeffs calcLowPass (float freq, float Q, float sampleRate)
{
    float w0 = juce::MathConstants<float>::twoPi * freq / sampleRate;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    
    BiquadCoeffs c;
    float b0 = (1.0f - cosw0) / 2.0f;
    float b1 = 1.0f - cosw0;
    float b2 = (1.0f - cosw0) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;
    
    c.a0 = b0 / a0;
    c.a1 = b1 / a0;
    c.a2 = b2 / a0;
    c.b1 = a1 / a0;
    c.b2 = a2 / a0;
    return c;
}

static BiquadCoeffs calcLowShelf (float freq, float Q, float gainDb, float sampleRate)
{
    float A = std::pow (10.0f, gainDb / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / sampleRate;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    
    BiquadCoeffs c;
    float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha);
    float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
    float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha);
    float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha;
    float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
    float a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha;
    
    c.a0 = b0 / a0;
    c.a1 = b1 / a0;
    c.a2 = b2 / a0;
    c.b1 = a1 / a0;
    c.b2 = a2 / a0;
    return c;
}

static BiquadCoeffs calcHighShelf (float freq, float Q, float gainDb, float sampleRate)
{
    float A = std::pow (10.0f, gainDb / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / sampleRate;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    
    BiquadCoeffs c;
    float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha);
    float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha);
    float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha;
    float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    float a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha;
    
    c.a0 = b0 / a0;
    c.a1 = b1 / a0;
    c.a2 = b2 / a0;
    c.b1 = a1 / a0;
    c.b2 = a2 / a0;
    return c;
}

static BiquadCoeffs calcPeakingEQ (float freq, float Q, float gainDb, float sampleRate)
{
    float A = std::pow (10.0f, gainDb / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / sampleRate;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    
    BiquadCoeffs c;
    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;
    
    c.a0 = b0 / a0;
    c.a1 = b1 / a0;
    c.a2 = b2 / a0;
    c.b1 = a1 / a0;
    c.b2 = a2 / a0;
    return c;
}

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

        case 1: // Vocals — FabFilter Pro-Q 3 curve + Studio FET compression
            eqCoeffs[0] = calcHighPass  (80.0f,    0.707f,              currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (120.0f,   0.707f, mix * 3.5f,  currentSampleRate);       // Low warmth boost
            eqCoeffs[2] = calcPeakingEQ (450.0f,   2.5f,   mix * -3.5f, currentSampleRate);       // Boxy cut (narrow Q)
            eqCoeffs[3] = calcPeakingEQ (2500.0f,  1.5f,   mix * 2.5f,  currentSampleRate);       // Presence boost
            eqCoeffs[4] = calcPeakingEQ (5500.0f,  2.0f,   mix * -2.5f, currentSampleRate);       // High mid cut
            eqCoeffs[5] = calcHighShelf (10000.0f, 0.707f, mix * 3.5f,  currentSampleRate);       // Air shelf
            break;

        case 2: // Piano/Keys — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (60.0f,    0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (180.0f,   0.707f, mix * 1.0f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (400.0f,   1.6f,   mix * -1.0f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (2500.0f,  1.4f,   mix * 1.0f,  currentSampleRate);       // definition
            eqCoeffs[4] = calcHighShelf (10000.0f, 0.707f, mix * 1.5f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (20000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

#if INSTANT_MIX_PRO
        case 3: // Electric Guitar — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (100.0f,   0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (250.0f,   0.707f, mix * 1.0f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (500.0f,   1.8f,   mix * -1.0f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (4000.0f,  1.6f,   mix * 1.5f,  currentSampleRate);       // presence
            eqCoeffs[4] = calcHighShelf (10000.0f, 0.707f, mix * 1.0f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (20000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

        case 4: // Drums — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (50.0f,    0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (150.0f,   0.707f, mix * 1.5f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (300.0f,   1.8f,   mix * -1.5f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (5000.0f,  1.4f,   mix * 1.0f,  currentSampleRate);       // presence
            eqCoeffs[4] = calcHighShelf (12000.0f, 0.707f, mix * 1.5f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (20000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

        case 5: // Bass — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (40.0f,    0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (120.0f,   0.707f, mix * 1.5f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (250.0f,   1.8f,   mix * -1.0f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (800.0f,   1.4f,   mix * 1.0f,  currentSampleRate);       // presence
            eqCoeffs[4] = calcHighShelf (8000.0f,  0.707f, mix * 0.5f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (12000.0f, 0.707f,              currentSampleRate);        // LPF
            break;
#endif

        default:
            for (auto& c : eqCoeffs) { c.a0=1.f; c.a1=c.a2=c.b1=c.b2=0.f; }
            break;
    }
}

void LeviathexInstantMixerAudioProcessor::processBiquad (float& sample, BiquadState& state, const BiquadCoeffs& coeffs)
{
    sample = state.process (sample, coeffs);
}

void LeviathexInstantMixerAudioProcessor::applyExciter (float& sample, ExciterState& state, float intensity)
{
    // Tape-style saturation with pre/de-emphasis
    // Intensity: 0.0 = bypass, 1.0 = full effect
    
    if (intensity <= 0.0f)
        return;
    
    // DC blocker (high-pass at 20Hz)
    const float dcAlpha = 0.995f;
    float dcIn = sample;
    float dcOut = dcIn - state.dcBlockerX1 + dcAlpha * state.dcBlockerY1;
    state.dcBlockerX1 = dcIn;
    state.dcBlockerY1 = dcOut;
    float x = dcOut;
    
    // Pre-emphasis (boost highs before saturation)
    const float preAlpha = 0.8f;
    float preOut = x - preAlpha * state.preEmphasisX1 + preAlpha * state.preEmphasisY1;
    state.preEmphasisX1 = x;
    state.preEmphasisY1 = preOut;
    
    // Tape saturation using soft clipping
    // Curve: tanh-based with gentle knee
    float drive = 1.0f + intensity * 2.0f; // 1x to 3x drive
    float saturated = std::tanh (preOut * drive);
    
    // Blend between dry and saturated based on intensity
    float blended = preOut + (saturated - preOut) * intensity;
    
    // De-emphasis (gentle low-pass to restore balance)
    const float deAlpha = 0.3f;
    float deOut = blended * (1.0f - deAlpha) + state.deEmphasisY1 * deAlpha;
    state.deEmphasisX1 = blended;
    state.deEmphasisY1 = deOut;
    
    sample = deOut;
}

void LeviathexInstantMixerAudioProcessor::applyCompressor (float& sampleL, float& sampleR, float mix, int instrument)
{
    // Per-instrument compressor settings (at 100% mix)
    float targetThreshDb, targetRatio, targetAttackMs, targetReleaseMs, targetMakeupDb;
    
    switch (instrument)
    {
        case 1: // Vocals - Studio FET settings from screenshot
            targetThreshDb = -20.0f;
            targetRatio = 5.0f;
            targetAttackMs = 20.0f;
            targetReleaseMs = 100.0f;
            targetMakeupDb = 5.0f;
            break;
            
        default: // All other instruments - API 2500 settings
            targetThreshDb = -18.0f;
            targetRatio = 4.0f;
            targetAttackMs = 0.3f;
            targetReleaseMs = 100.0f;
            targetMakeupDb = 0.0f;
            break;
    }
    
    // Scale parameters based on mix (0% = no compression, 100% = full settings)
    float threshDb = mix * targetThreshDb;  // Interpolate from 0 to target
    float ratio = 1.0f + mix * (targetRatio - 1.0f);  // Interpolate from 1:1 to target
    float threshLin = juce::Decibels::decibelsToGain (threshDb);
    
    // Calculate attack/release coefficients based on scaled timing
    float attackMs = mix * targetAttackMs;
    float releaseMs = mix * targetReleaseMs;
    
    // Prevent division by zero and ensure minimum values
    attackMs = std::max (0.1f, attackMs);
    releaseMs = std::max (1.0f, releaseMs);
    
    float currentAttackCoeff = std::exp (-1.0f / (attackMs * currentSampleRate / 1000.0f));
    float currentReleaseCoeff = std::exp (-1.0f / (releaseMs * currentSampleRate / 1000.0f));
    
    // Calculate envelope for both channels
    float envL = std::abs (sampleL);
    float envR = std::abs (sampleR);
    if (! std::isfinite (envL)) envL = 0.0f;
    if (! std::isfinite (envR)) envR = 0.0f;
    
    // Stereo linking: use max of both channels for envelope detection
    float envIn = std::max (envL, envR);
    
    // Update linked envelope with dynamic coefficients
    if (envIn > linkedEnvelope)
        linkedEnvelope = currentAttackCoeff * linkedEnvelope + (1.0f - currentAttackCoeff) * envIn;
    else
        linkedEnvelope = currentReleaseCoeff * linkedEnvelope + (1.0f - currentReleaseCoeff) * envIn;
    
    // Calculate gain reduction (same for both channels - true stereo linking)
    float gainReduction = 1.0f;
    float reducDb = 0.0f;
    if (linkedEnvelope > threshLin && mix > 0.001f)
    {
        float inputDb  = 20.0f * std::log10 (linkedEnvelope + 1e-10f);
        float excessDb = inputDb - threshDb;
        reducDb  = excessDb * (1.0f - 1.0f / ratio);
        gainReduction  = juce::Decibels::decibelsToGain (-reducDb);
    }
    
    // Store gain reduction for metering (use atomic for thread safety)
    gainReductionDb.store (reducDb);
    
    // Apply same gain reduction to both channels (stereo linked)
    sampleL *= gainReduction;
    sampleR *= gainReduction;
    
    // Apply makeup gain (scaled with mix)
    float makeupGain = juce::Decibels::decibelsToGain (mix * targetMakeupDb);
    sampleL *= makeupGain;
    sampleR *= makeupGain;
    
    // Safety: kill any NaN/Inf
    if (! std::isfinite (sampleL)) sampleL = 0.0f;
    if (! std::isfinite (sampleR)) sampleR = 0.0f;
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

void LeviathexInstantMixerAudioProcessor::configureReverbForInstrument (int instrument, float length)
{
    // Map length (0.5-4.0s) to room size (0.0-1.0)
    float roomSize = (length - 0.5f) / 3.5f;
    
    switch (instrument)
    {
        case 0: // Acoustic - Room style
            reverbParams.roomSize = roomSize * 0.7f + 0.2f; // 0.2-0.9
            reverbParams.damping = 0.6f;
            reverbParams.width = 0.8f;
            break;
            
        case 1: // Vocal - Plate style
            reverbParams.roomSize = roomSize * 0.5f + 0.3f; // 0.3-0.8
            reverbParams.damping = 0.4f;
            reverbParams.width = 0.6f;
            break;
            
        case 2: // Piano - Hall style
            reverbParams.roomSize = roomSize * 0.8f + 0.2f; // 0.2-1.0
            reverbParams.damping = 0.3f;
            reverbParams.width = 1.0f;
            break;
            
#if INSTANT_MIX_PRO
        case 3: // Electric Guitar - Small Room style
            reverbParams.roomSize = roomSize * 0.5f + 0.2f; // 0.2-0.7
            reverbParams.damping = 0.7f;
            reverbParams.width = 0.5f;
            break;
            
        case 4: // Drums - Large Room/Studio style
            reverbParams.roomSize = roomSize * 0.6f + 0.3f; // 0.3-0.9
            reverbParams.damping = 0.5f;
            reverbParams.width = 0.9f;
            break;
            
        case 5: // Bass - Tight/Small style
            reverbParams.roomSize = roomSize * 0.3f + 0.1f; // 0.1-0.4
            reverbParams.damping = 0.8f;
            reverbParams.width = 0.3f;
            break;
#endif
            
        default:
            reverbParams.roomSize = roomSize;
            reverbParams.damping = 0.5f;
            reverbParams.width = 1.0f;
            break;
    }
    
    reverbProcessor.setParameters (reverbParams);
}

void LeviathexInstantMixerAudioProcessor::processReverb (juce::AudioBuffer<float>& dryBuffer, juce::AudioBuffer<float>& reverbBuffer, int numSamples, float wetLevel)
{
    // Copy dry to reverb buffer for processing
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* dry = dryBuffer.getReadPointer (ch);
        float* wet = reverbBuffer.getWritePointer (ch);
        std::copy (dry, dry + numSamples, wet);
    }
    
    // Process reverb
    reverbProcessor.processStereo (reverbBuffer.getWritePointer (0), reverbBuffer.getWritePointer (1), numSamples);
    
    // Apply wet level
    if (wetLevel < 1.0f)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* wet = reverbBuffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] *= wetLevel;
        }
    }
}

float LeviathexInstantMixerAudioProcessor::calculateRMS (const float* buffer, int numSamples)
{
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sum += buffer[i] * buffer[i];
    
    return std::sqrt (sum / numSamples);
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

float LeviathexInstantMixerAudioProcessor::calculateShortTermLUFS (const float* left, const float* right, int numSamples)
{
    // Simplified LUFS calculation using K-weighting approximation
    // True LUFS requires precise K-weighting filters, this is a practical approximation
    
    float sumSquared = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        // Convert to LUFS scale (sum of squares of both channels)
        float l = left[i];
        float r = right[i];
        sumSquared += (l * l) + (r * r);
    }
    
    // Mean square
    float meanSquare = sumSquared / (numSamples * 2.0f);
    
    // Convert to dB (with -0.691 + 10*log10 calibration offset approximation)
    if (meanSquare > 0.0f)
        return 10.0f * std::log10 (meanSquare) - 0.691f;
    else
        return -70.0f;
}

void LeviathexInstantMixerAudioProcessor::updateMakeupGain (float mixValue, bool autoMakeupEnabled)
{
    if (!autoMakeupEnabled)
    {
        makeupGainSmoother.setTargetValue (1.0f);
        return;
    }
    
    // Calculate target LUFS (-14 LUFS is a common streaming target)
    constexpr float targetLUFS = -14.0f;
    
    // Estimate loudness change based on mix knob position
    // Higher mix = more processing = typically more loudness
    float mixLinear = mixValue / 100.0f;
    float estimatedLoudnessChange = mixLinear * 3.0f; // Up to +3dB at full mix
    
    // Calculate required makeup gain
    float currentEstimatedLUFS = measuredInputLUFS + estimatedLoudnessChange;
    float lufsDifference = targetLUFS - currentEstimatedLUFS;
    
    // Convert dB difference to gain factor (limited to reasonable range)
    float makeupDb = juce::jlimit (-6.0f, 6.0f, lufsDifference);
    float makeupGain = juce::Decibels::decibelsToGain (makeupDb);
    
    makeupGainSmoother.setTargetValue (makeupGain);
}

void LeviathexInstantMixerAudioProcessor::addLogMessage (const juce::String& message, const juce::String& level)
{
    std::lock_guard<std::mutex> lock (logMutex);
    
    LogEntry entry;
    entry.timestamp = juce::Time::getCurrentTime().toString (true, true);
    entry.level = level;
    entry.message = message;
    
    logEntries.push_back (entry);
    
    // Keep only last 100 entries to prevent memory bloat
    if (logEntries.size() > 100)
        logEntries.erase (logEntries.begin());
}

juce::String LeviathexInstantMixerAudioProcessor::getLogAsString() const
{
    std::lock_guard<std::mutex> lock (logMutex);
    
    juce::String result;
    for (const auto& entry : logEntries)
    {
        result += "[" + entry.timestamp + "] [" + entry.level + "] " + entry.message + "\n";
    }
    return result;
}

void LeviathexInstantMixerAudioProcessor::clearLog()
{
    std::lock_guard<std::mutex> lock (logMutex);
    logEntries.clear();
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
