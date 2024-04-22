/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SawtoothOscillator.h"
#include "BandpassFilter.h"
#include "EnvelopeFollower.h"

//==============================================================================
/**
*/
class VocVoiceTestAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    VocVoiceTestAudioProcessor();
    ~VocVoiceTestAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

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

private:
    float level;

    //Sawtooth
    std::map<int, std::unique_ptr<SawtoothOscillator>> oscillators;
    juce::dsp::ProcessSpec spec;

    //BandPassFilter
    BandPassFilter bandPassFilter;
    juce::AudioProcessorValueTreeState parameters;
    float qFactor;


    //EnvelopeFollower
    EnvelopeFollower envFollower;
    float attackTime;
    float releaseTime;

    juce::AudioBuffer<float> oscillatorBuffer;
    juce::AudioBuffer<float> bandPassBuffer;
    juce::AudioBuffer<float> envelopeBuffer;
    juce::AudioBuffer<float> processedBuffer;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocVoiceTestAudioProcessor)
};