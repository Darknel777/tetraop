#pragma once

#include "Engine/MinimalVoiceCore.h"
#include "Parameters/StateDocument.h"

#include <JuceHeader.h>
#include <array>
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
    int getActiveVoiceCountForPart(int partNumber) const noexcept;
    int getActiveTailCount() const noexcept;
    bool isNoteActive(int partNumber, int channel, int noteNumber) const noexcept;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    juce::String saveStateDocument() const;
    bool restoreStateDocument(const juce::String& document);

private:
    struct PartSlots
    {
        juce::AudioParameterBool* enabled = nullptr;
        juce::AudioParameterBool* mono = nullptr;
        juce::AudioParameterBool* legato = nullptr;
        juce::AudioParameterChoice* midiChannel = nullptr;
        std::array<juce::AudioParameterChoice*, 3> wave {};
        juce::AudioParameterFloat* osc1Level = nullptr;
        juce::AudioParameterFloat* osc2Level = nullptr;
        juce::AudioParameterFloat* ringLevel = nullptr;
        juce::AudioParameterFloat* noiseLevel = nullptr;
        juce::AudioParameterFloat* directOut = nullptr;
        juce::AudioParameterFloat* attack = nullptr;
        juce::AudioParameterFloat* decay = nullptr;
        juce::AudioParameterFloat* sustain = nullptr;
        juce::AudioParameterFloat* release = nullptr;
        juce::AudioParameterChoice* filterType = nullptr;
        juce::AudioParameterFloat* cutoff = nullptr;
        juce::AudioParameterFloat* resonance = nullptr;
        juce::AudioParameterFloat* drive = nullptr;
        juce::AudioParameterFloat* keytrack = nullptr;
        juce::AudioParameterFloat* filterMix = nullptr;
        juce::AudioParameterFloat* filterAttack = nullptr;
        juce::AudioParameterFloat* filterDecay = nullptr;
        juce::AudioParameterFloat* filterSustain = nullptr;
        juce::AudioParameterFloat* filterRelease = nullptr;
        juce::AudioParameterFloat* filterAmount = nullptr;
        juce::AudioParameterFloat* lfo1ToCutoff = nullptr;
        juce::AudioParameterFloat* lfo2ToPitch = nullptr;
        struct LfoSlots
        {
            juce::AudioParameterChoice* shape = nullptr;
            juce::AudioParameterBool* retrig = nullptr;
            juce::AudioParameterChoice* polarity = nullptr;
            juce::AudioParameterBool* sync = nullptr;
            juce::AudioParameterFloat* rateHz = nullptr;
            juce::AudioParameterChoice* rateSync = nullptr;
            juce::AudioParameterFloat* freeVariation = nullptr;
        };
        std::array<LfoSlots, xenmilenium::MinimalVoiceCore::lfoCount> lfo {};
    };

    struct MatrixParams
    {
        juce::AudioParameterChoice* source = nullptr;
        juce::AudioParameterChoice* destination = nullptr;
        juce::AudioParameterFloat* amount = nullptr;
        juce::AudioParameterBool* enabled = nullptr;
    };

    void handleMidi(const juce::MidiBuffer& midi);
    void syncPartEnables() noexcept;
    void applyMidiToCore(const juce::MidiMessage& message) noexcept;
    void renderRange(juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept;
    void updateTempo() noexcept;
    void cacheMatrixAndModes() noexcept;
    xenmilenium::EngineControls readControls() const noexcept;
    static bool partAcceptsChannel(const xenmilenium::PartControls& part, int channel) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<xenmilenium::StoredDocument> document;
    xenmilenium::MinimalVoiceCore voiceCore;
    juce::AudioParameterFloat* masterVolume = nullptr;
    std::array<PartSlots, xenmilenium::MinimalVoiceCore::partCount> partSlots {};
    std::array<juce::AudioParameterChoice*, xenmilenium::msegModuleCount> msegModeParams {};
    std::array<MatrixParams, xenmilenium::modulationSlotCount> matrixParams {};
    std::array<xenmilenium::MatrixSlotControl, xenmilenium::modulationSlotCount> matrixCache {};
    std::array<int, xenmilenium::msegModuleCount> modeCache {};
    std::array<bool, xenmilenium::modulationPartCount> matrixActiveCache {};
    std::atomic<int> lastNoteNumber { -1 };
    std::atomic<int> noteOnCount { 0 };
    double currentSampleRate = 0.0;
    float currentTempoBpm = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XenMileniumAudioProcessor)
};
