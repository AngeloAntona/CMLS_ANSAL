
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SimpleCompressor.h"
#include "HighPassFilter.h"

//==============================================================================
PolyPhaseVoc2AudioProcessor::PolyPhaseVoc2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    activeNotes.resize(maxVoices, -1);  // Initialize all voices as inactive

    // Initialize default Compressor Parameters
    ratio = 4.0f;
    threshold = 0.7f;

    // Initialize default EnvelopeGenerator parameters
    attack = 0.2f;  // Attack time in seconds
    decay = 0.2f;   // Decay time in seconds
    sustain = 0.7f; // Sustain level (0.0 to 1.0)
    release = 0.3f; // Release time in seconds

    // Initialize default PhaseVoc parameters
    corr_k = 0.9992f; // Correlation coefficient between old and current samples of the vocoder
}

PolyPhaseVoc2AudioProcessor::~PolyPhaseVoc2AudioProcessor()
{
}

//==============================================================================
const juce::String PolyPhaseVoc2AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PolyPhaseVoc2AudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PolyPhaseVoc2AudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PolyPhaseVoc2AudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PolyPhaseVoc2AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PolyPhaseVoc2AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PolyPhaseVoc2AudioProcessor::getCurrentProgram()
{
    return 0;
}

void PolyPhaseVoc2AudioProcessor::setCurrentProgram(int index)
{
}

const juce::String PolyPhaseVoc2AudioProcessor::getProgramName(int index)
{
    return {};
}

void PolyPhaseVoc2AudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void PolyPhaseVoc2AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{

    highPassFilter.prepareToPlay(sampleRate, samplesPerBlock);
    highPassFilter.setCutoffFrequency(highPassCutoff);

    updateCompressorParameters();
    updateEnvelopeParameters();
    updatePhaseVocParameters();

}

void PolyPhaseVoc2AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PolyPhaseVoc2AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void PolyPhaseVoc2AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();
    juce::AudioBuffer<float> tempBufferTot(buffer.getNumChannels(), numSamples);
    tempBufferTot.clear();

    // Process MIDI messages
    for (const auto& midiMessage : midiMessages) {
        handleMidiEvent(midiMessage.getMessage());
    }

    // Mix outputs from active vocoders
    for (int i = 0; i < maxVoices; ++i) {
        if (activeNotes[i] != -1 || vocoders[i].envGen.isActive()) { // Continue processing if note is active or envelope is active

            juce::AudioBuffer<float> tempBuffer(1, numSamples);  // Assuming vocoder output is mono.
            tempBuffer.clear();

            comp.process(buffer.getReadPointer(0), tempBuffer.getWritePointer(0), numSamples);
            vocoders[i].process(tempBuffer.getReadPointer(0), tempBuffer.getWritePointer(0), numSamples);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
                tempBufferTot.addFrom(channel, 0, tempBuffer, 0, 0, numSamples);
            }

            if (!vocoders[i].envGen.isActive()) {
                activeNotes[i] = -1; // Only mark as inactive if envelope has fully completed
            }
        }
    }

    // Copy all the samples from tempBufferTot to buffer
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        buffer.copyFrom(channel, 0, tempBufferTot, channel, 0, numSamples);
    }

    // Apply the high-pass filter
    highPassFilter.processBlock(buffer);

    if (auto* editor = dynamic_cast<PolyPhaseVoc2AudioProcessorEditor*>(getActiveEditor()))
    {
        editor->pushDataToVisualiser(buffer);
    }
}

//==============================================================================
bool PolyPhaseVoc2AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PolyPhaseVoc2AudioProcessor::createEditor()
{
    return new PolyPhaseVoc2AudioProcessorEditor(*this);
}

//==============================================================================
void PolyPhaseVoc2AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void PolyPhaseVoc2AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolyPhaseVoc2AudioProcessor();
}

void PolyPhaseVoc2AudioProcessor::handleMidiEvent(const juce::MidiMessage& msg) {
    if (msg.isNoteOn()) {
        int noteNumber = msg.getNoteNumber();
        float frequency = 440.0f * std::pow(2.0f, (noteNumber - 69) / 12.0f);
        float newDelta = frequency * (static_cast<float>(0xFFFFFFFF) / getSampleRate());
        bool voiceAllocated = false;

        // Find an inactive voice or reuse the oldest one
        for (int i = 0; i < maxVoices; ++i) {
            if (activeNotes[i] == -1) {
                vocoders[i].setDelta(newDelta);
                vocoders[i].noteOn(); // Start the envelope
                activeNotes[i] = noteNumber;
                voiceAllocated = true;
                break;
            }
        }

        if (!voiceAllocated) {
            vocoders[0].setDelta(newDelta);
            vocoders[0].noteOn(); // Start the envelope on the reused voice
            activeNotes[0] = noteNumber;
        }
    }
    else if (msg.isNoteOff()) {
        int noteNumber = msg.getNoteNumber();
        for (int i = 0; i < maxVoices; ++i) {
            if (activeNotes[i] == noteNumber) {
                vocoders[i].noteOff(); // Stop the envelope
                activeNotes[i] = -1; // Mark this voice as inactive
                break;
            }
        }
    }
    else if (msg.isController()) { // Only respond to messages on channel 1
        int controllerNumber = msg.getControllerNumber();
        int controllerValue = msg.getControllerValue();
        float normalizedValue = controllerValue / 127.0f;

        switch (controllerNumber) {
        case 22: {// MIDI controller 22 modifies the ratio
            setRatio(normalizedValue * 19.0f + 1.1f); // Map to ratio range 1.1 to 20.0
                DBG("Updated ratio to: " << mappedValue);
            if (ratioSlider)
                ratioSlider->setValue(normalizedValue * 19.0f + 1.1f, juce::NotificationType::dontSendNotification);
            break;
        }
        case 23: { // MIDI controller 23 modifies corr_k
            float mappedValue = normalizedValue * 0.0049f + 0.9950f; // Map to corr_k range 0.9950 to 0.9999
            setCorr(mappedValue); // Update the processor's corr_k parameter
            if (corrSlider)
                corrSlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated corr_k to: " << mappedValue);
            break;
        }
        case 24: { // MIDI controller 24 modifies highPassCutoff
            float mappedValue = normalizedValue * 1000.0f; // Map to highPassCutoff range 0 to 1000 Hz
            setHighPassCutoff(mappedValue); // Update the processor's highPassCutoff parameter
            if (highPassCutoffSlider)
                highPassCutoffSlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated highPassCutoff to: " << mappedValue);
            break;
        }
        case 26: { // MIDI controller 26 modifies attack
            float mappedValue = normalizedValue * 1.5f; // Map to attack range 0.0 to 1.5
            setAttack(mappedValue); // Update the processor's attack parameter
            if (attackSlider)
                attackSlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated attack to: " << mappedValue);
            break;
        }
        case 27: { // MIDI controller 27 modifies decay
            float mappedValue = normalizedValue * 2.0f; // Map to decay range 0.0 to 2.0
            setDecay(mappedValue); // Update the processor's decay parameter
            if (decaySlider)
                decaySlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated decay to: " << mappedValue);
            break;
        }
        case 28: { // MIDI controller 28 modifies sustain
            float mappedValue = normalizedValue; // Map to sustain range 0.0 to 1.0
            setSustain(mappedValue); // Update the processor's sustain parameter
            if (sustainSlider)
                sustainSlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated sustain to: " << mappedValue);
            break;
        }
        case 29: { // MIDI controller 29 modifies release
            float mappedValue = normalizedValue * 3.0f; // Map to release range 0.0 to 3.0
            setRelease(mappedValue); // Update the processor's release parameter
            if (releaseSlider)
                releaseSlider->setValue(mappedValue, juce::NotificationType::dontSendNotification); // Update the slider without sending notification
            DBG("Updated release to: " << mappedValue);
            break;
        }
        default:
            break;
        }
    }
}

void PolyPhaseVoc2AudioProcessor::updateCompressorParameters() {
    comp.setRatio(ratio);
    comp.setThreshold(threshold);
}

void PolyPhaseVoc2AudioProcessor::updateEnvelopeParameters() {
    for (int i = 0; i < maxVoices; ++i) {
        vocoders[i].envGen.setAttack(attack);
        vocoders[i].envGen.setDecay(decay);
        vocoders[i].envGen.setSustain(sustain);
        vocoders[i].envGen.setRelease(release);
    }
}

void PolyPhaseVoc2AudioProcessor::updatePhaseVocParameters() {
    for (int i = 0; i < maxVoices; ++i) {
        vocoders[i].setCorr_k(corr_k);
    }
}

void PolyPhaseVoc2AudioProcessor::setRatio(float newRatio) {
    ratio = newRatio;
    updateCompressorParameters();
}

void PolyPhaseVoc2AudioProcessor::setThreshold(float newThreshold) {
    newThreshold = newThreshold;
    updateCompressorParameters();
}

void PolyPhaseVoc2AudioProcessor::setAttack(float newAttack) {
    attack = newAttack;
    updateEnvelopeParameters();
}

void PolyPhaseVoc2AudioProcessor::setDecay(float newDecay) {
    decay = newDecay;
    updateEnvelopeParameters();
}

void PolyPhaseVoc2AudioProcessor::setSustain(float newSustain) {
    sustain = newSustain;
    updateEnvelopeParameters();
}

void PolyPhaseVoc2AudioProcessor::setRelease(float newRelease) {
    release = newRelease;
    updateEnvelopeParameters();
}

void PolyPhaseVoc2AudioProcessor::setCorr(float newCorr) {
    corr_k = newCorr;
    updatePhaseVocParameters();
}

void PolyPhaseVoc2AudioProcessor::setHighPassCutoff(float newCutoff) {
    highPassCutoff = newCutoff;
    highPassFilter.setCutoffFrequency(newCutoff);
}
