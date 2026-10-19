#pragma once

#include "Engine/MinimalVoiceCore.h"
#include "Parameters/StateDocument.h"

#include <JuceHeader.h>
#include <atomic>
#include <memory>

class XenMileniumAudioProcessor : public juce::AudioProcessor
{
public:
    XenMileniumAudioProcessor();
    ~XenMileniumAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int getLastNoteNumber() const noexcept;
    int getNoteOnCount() const noexcept;
    int getActiveVoiceCount() const noexcept;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    juce::String saveStateDocument() const;
    bool restoreStateDocument(const juce::String& document);

private:
    void handleMidi(const juce::MidiBuffer& midi);
    void applyMidiToCore(const juce::MidiMessage& message) noexcept;
    void renderRange(juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept;
    xenmilenium::AmplitudeControls readControls() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<xenmilenium::StoredDocument> document;
    xenmilenium::MinimalVoiceCore voiceCore;
    juce::AudioParameterFloat* masterVolume = nullptr;
    juce::AudioParameterChoice* waveSource = nullptr;
    juce::AudioParameterFloat* ampAttack = nullptr;
    juce::AudioParameterFloat* ampDecay = nullptr;
    juce::AudioParameterFloat* ampSustain = nullptr;
    juce::AudioParameterFloat* ampRelease = nullptr;
    juce::AudioParameterBool* partOneEnabled = nullptr;
    std::atomic<int> lastNoteNumber { -1 };
    std::atomic<int> noteOnCount { 0 };
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XenMileniumAudioProcessor)
};
